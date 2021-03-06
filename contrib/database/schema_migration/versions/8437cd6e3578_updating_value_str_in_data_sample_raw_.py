#
# Copyright (c) 2016 Intel Corporation. All rights reserved
#
"""Updating value_str in data_sample_raw table from VARCHAR(500) to TEXT

Revision ID: 8437cd6e3578
Revises: d109117b1990
Create Date: 2016-08-09 13:31:21.717736

"""

# revision identifiers, used by Alembic.
revision = '8437cd6e3578'
down_revision = 'd109117b1990'
branch_labels = None

from alembic import op
import sqlalchemy as sa
import textwrap

def postgres_drop_views():
    op.execute("DROP VIEW IF EXISTS data_samples_view;")
    op.execute("DROP VIEW IF EXISTS syslog_view;")
    op.execute("DROP VIEW IF EXISTS event_sensor_data_view;")
    op.execute("DROP VIEW IF EXISTS data_sensors_view;")

def postgres_recreate_views():
    op.execute(textwrap.dedent("""
        CREATE OR REPLACE VIEW data_samples_view AS
            SELECT data_sample_raw.hostname,
                data_sample_raw.data_item,
                data_sample_raw.data_type_id,
                data_sample_raw.time_stamp,
                data_sample_raw.value_int,
                data_sample_raw.value_real,
                data_sample_raw.value_str,
                data_sample_raw.units
            FROM data_sample_raw;
        """))
    op.execute(textwrap.dedent("""
        CREATE OR REPLACE VIEW syslog_view AS
	        SELECT data_sample_raw.hostname,
                data_sample_raw.data_item,
                data_sample_raw.time_stamp::text AS time_stamp,
                data_sample_raw.value_str AS log
            FROM data_sample_raw
            WHERE data_sample_raw.data_item::text ~~ 'syslog_log%'::text;
        """))
    op.execute(textwrap.dedent("""
	    CREATE OR REPLACE VIEW event_sensor_data_view AS
            SELECT data_sample_raw.time_stamp::text AS time_stamp,
                data_sample_raw.hostname,
                data_sample_raw.data_item,
                concat(data_sample_raw.value_int::text, data_sample_raw.value_real::text, data_sample_raw.value_str) AS value,
                data_sample_raw.units
            FROM data_sample_raw;
        """))
    op.execute(textwrap.dedent("""
	    CREATE OR REPLACE VIEW data_sensors_view AS
            SELECT dsr.hostname,
                dsr.data_item,
                concat(dsr.time_stamp) AS time_stamp,
                concat(dsr.value_int, dsr.value_real, dsr.value_str) AS     value_str,dsr.units
            FROM data_sample_raw dsr;
	    """))


def drop_views():
    db_dialect = op.get_context().dialect
    if 'postgresql' in db_dialect.name:
        postgres_drop_views()
        return True
    else:
        print("Views were not dropped. '%s' is not a supported database dialect." % db_dialect.name)
        return False

def recreate_views():
    db_dialect = op.get_context().dialect
    if 'postgresql' in db_dialect.name:
        postgres_recreate_views()
        return True
    else:
        print("Views were not re-created. '%s' is not a supported database dialect." % db_dialect.name)
        return False

def upgrade():
    if not drop_views():
        print("Error: DB Dialect is not supported, aborting upgrade!")
        return
    op.alter_column('data_sample_raw', 'value_str',
               existing_type=sa.VARCHAR(length=500),
               type_=sa.Text(),
               existing_nullable=True)
    recreate_views()


def downgrade():
    if not drop_views():
        print("Error: DB Dialect is not supported, aborting downgrade!")
        return
    op.alter_column('data_sample_raw', 'value_str',
               existing_type=sa.Text(),
               type_=sa.VARCHAR(length=500),
               existing_nullable=True)
    recreate_views()
